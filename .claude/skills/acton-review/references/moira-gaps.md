# Moira under the data-oriented lens: known gaps

Audited 2026-10-11 at M6 step 1 (commit `23f4142`). Sizes were measured with GCC 13 on
x86-64. Each gap names its path class (SKILL.md, review step 1) and what a console
technical director is likely to ask. None of these is a decision yet. When one is fixed,
or accepted as a deviation, record it in `docs/decisions.md` and update this file.

## Already aligned (keep it that way)

- `Node` is 16 bytes and `Array` is a 24-byte header with its children inline, both pinned
  by `static_assert`.
- Function, variable, object-name, and class lookup go through flat per-Context tables
  indexed by `Symbol::index()` (D44). No hashing on dispatch.
- Lazy evaluation means no temporary argument lists and no allocation per call. Calls,
  methods, and `get`/`set` are proven allocation-free by `alloc_counter` tests.
- `Tweak<float>` reads compile to one relaxed load, checked by a codegen test.
- No RTTI is needed (D49), and `MOIRA_NO_EXCEPTIONS` and `MOIRA_SHIPPING` builds run in CI.
- Object references are salted handles into a flat slot table (D4, D19), not pointers.
- Every performance decision carries before/after numbers (D47, D51, D52).

## Gaps

1. **There is no allocator hook (load, hot).** Arrays and strings come from global
   `::operator new` (`src/data/array.cpp`, `src/data/node.cpp`). Tables use `std::vector`,
   `std::string`, `std::unordered_map`, `std::shared_ptr`, and `std::function`. A console
   team will ask:
   - which heap does each allocation come from;
   - can a level's data be freed as one block?

   The smallest useful step is to route the two raw `operator new` sites through one
   replaceable allocator (a pair of functions in the D45 hook style), and give M8's cache
   an arena.
2. **Hot and cold fields share records (hot).** `detail::MethodEntry` is 88 bytes:
   - the name, overloads, doc string, status, and an inherited flag all sit together;
   - method dispatch binary-searches an array of these, so each probe touches a new
     88-byte stride for an 8-byte name.

   `Function` (96 bytes) likewise mixes `overloads` with `doc`, `file`, and `line`.
   Splitting the hot keys and thunks from the cold documentation fixes both: the talk's
   "keys together, values elsewhere".
2b. **Lookup by tag reads one heap array per child (hot, load).** `Array::try_find`
   scans the children, and for each array child dereferences a separate heap `Array` just
   to read its tag (`include/moira/array.hpp`). Keys are not together: this is the
   talk's dictionary example, where each probe pays for the value's cache line. The
   `find_float` benchmark (§7, 25 ns) runs warm and does not show it. Remedies:
   - a contiguous tag array beside the children, built at load or bake time;
   - laying child arrays out adjacent to their parent in the binary cache (M8's "children
     adjacent to parents");
   - sorted mode (D28) for wide arrays.

   Measure cold, with the cache flushed or a working set larger than L2, before choosing.
3. **Dispatch chases pointers (hot).** A native call loads, one after another:
   - `by_index[i]` (a `Function*`);
   - the `Function`, for its `overloads` storage pointer;
   - the `Overload`, for `thunk` and the callable's address;
   - the callable object itself, inside the thunk.

   That is four dependent loads before the user's function runs. It costs 34 ns
   today, which meets §7, but this is where the remaining time goes. For a
   single-overload function, an inline thunk and callable pointer in the table entry would
   remove two of the loads.
4. **The API handles one item at a time (hot).**
   - `ctx.call`, the planned `obj.handle(msg)`, and `Tweak` reads all take one item.
   - There is no way to send a message to N objects, evaluate a command for N objects, or
     step N tasks as one batch.

   "Where there is one there are many": M6's typed messages are the natural place for a
   span-taking `handle_all`, grouped by type so each type's table and thunk stay in cache.
5. **Refcounts on every array (load, hot).** Each `Array` has its own refcount (atomic by
   default, D20). For a 5 MB tree loaded from the cache, per-array counts and frees cost
   bandwidth and teardown time. One owner per load (an arena with no per-node counts) is
   what the M8 spec implies. Make sure M8 does not inherit per-node refcounting.
6. **Objects carry a vtable and a heap side-table (hot).** `ObjectBase` is 32 bytes, with
   a vtable for `type()` and a `unique_ptr` to loose parameters. A per-entity loop over
   bound objects will be pointer-chasing into scattered heap objects. Moira binds the
   game's objects and does not own them, so this is partly the game's problem. Still,
   check that dispatch never iterates objects itself.
7. **Coroutine frames (M7, not built yet).** C++20 coroutine frames are heap-allocated
   unless `operator new` is supplied on the promise type. M7's plan must name the pool or
   allocator, the frame size budget, and how many tasks are live per frame.
8. **Exceptions are on by default.** The non-throwing twins exist, but the default build
   throws. A console project will build with `MOIRA_NO_EXCEPTIONS`. Keep every new API
   twinned, and keep any `try` off the hot path (D52).
9. **`Error` is 112 bytes, with strings and a vector (cold).** This is fine on the error
   path, but `Expected<Node>` is as large as `Error`, so every call returns a 112+ byte
   object. It measured at about 1 ns per call in D52. Watch it if `Error` grows again;
   boxing the error would shrink every return to the size of `Node` plus a tag.
10. **Tooling cost lives in the core (tool).** The edit-mode side tables, write-back, the
    reference, and `check` share libraries with the runtime. That is fine while they stay
    behind `MOIRA_SHIPPING` and off the hot structures. Flag it if a hot struct ever gains
    a field only tools use.
