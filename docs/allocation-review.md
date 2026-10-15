# Ownership and allocation review

*2026-10-14, after D126. The owner asked for a review of ownership and allocation with
gap 31 at its center. Gap 31 asks whether a Config's merged trees should leave the default
allocator for an arena the Config owns. This note is the evidence and a recommendation;
the decision goes in a D-entry.*

Evidence comes from:
- two allocation censuses, one per layer, each with a counting default allocator and a
  counting owner allocator, and every call site traced by its stack;
- two prototypes of merge, measured in a driver.

Debug-build counts are counts, not timings. Timings are release builds pinned to one CPU.

## The answer, first

**Keep merged trees as solo blocks, and fix merge instead.** The cost gap 31 points at is
real, but it is not where the blocks come from:

- **The merged tree is mostly copies.** About 7 of every 9 arrays merge rebuilds are
  identical, node for node, to the layer's own array. Sharing that array instead
  (prototype A1) takes a three-layer merged tree from 176,744 B to 12,184 B.
- **Most of a merge's allocations are its temporaries.** That is 77% of them: `Vec`s per
  rebuilt array and per value list, and an `unordered_map` node per key of a large layer.
  Reusing scratch buffers and a flat index (prototype A2) takes a merge from 9,067
  allocations to 19.

| Three file layers of about 36 KB, one full merge | allocations | merged tree | time |
|---|---:|---:|---:|
| today | 9,067 | 176,744 B | 365–370 µs |
| A1: share an array when the merge result is the layer's own | 6,961 | 12,184 B | 284–287 µs |
| A1 + A2: also reuse temporaries and a flat index | **19** | **12,184 B** | **155–157 µs** |

After A1 and A2, an arena per merge would save about 19 allocations a merge. It would also
bring every hazard in the next section. A real A2 would not use the prototype's
`thread_local` buffers: the Config would own its merge scratch on its own allocator, so a
merge reaches the default allocator for the merged tree's few blocks only.

## Gap 31's three options

| | A. Fix merge, keep solo | B. An arena per merge | C. Solo blocks from the Config's allocator |
|---|---|---|---|
| Allocations a merge | ~19 (measured, A1 + A2) | ~10 blocks, plus the temporaries unless A2 is done too | as A |
| Heap | default, ~12 KB | the Config's | the Config's |
| What a held value pins | itself and the layer arenas it points into, as today | **the whole merged arena** and every layer arena it points into | as A |
| `Array::set` through `root()` | frees the replaced node | **keeps it until the arena dies** (measured: +88 B per set) | as A |
| Garbage of a merge | none (A1 removes most rebuilding) | stays in the arena | none |
| Cost to build | merge.cpp; the Config owns a scratch | merge into an arena; a dep list that is linear in distinct solo owners | an allocator pointer in every solo block (8 B), or a per-owner solo kind |
| Changes D64 | no | yes | yes (solo blocks gain an owner) |

**B's hazards, measured or read in the census:**

1. **Retention.** Holding one 80 B leaf from `get<Node>` would pin about 1.1 MB: the merged
   arena and, through its deps, every layer arena it references. Today the same leaf
   pins 80 B. A host reading values from a Config would not expect that.
2. **No shedding.** 28.5% of the arrays a merge builds today are dead by its end, because
   `merged()` merges pairwise, one layer at a time. An arena keeps that waste until it dies.
3. **Mutation leaks.** `root()` hands out a non-const `Array`. On an arena array, `set`
   cannot free the replaced node.
4. **Contention.** Threads copying nodes of the merged tree would share one atomic count,
   where today each array has its own. D64 accepted this trade for loads.

**C** puts the merged tree on the Config's heap and keeps solo lifetimes exact. That is
worth doing only if a game budgets heaps per subsystem and needs the Config's few KB of
merged tree attributed to it. After A, the tree is about 12 KB, so C can wait for that
need.

## What else the census found, ranked by cost

The hot paths are clean. Script calls, `Context::call`, task ticks, Config reads and the
`set` fast path all make **0 allocations** after warm-up on both allocators (measured
over 1,000 iterations each). Everything below is load, tool, or error path.

1. **Source maps in edit mode, the largest single item.** Edit mode, the default outside
   shipping, keeps about 466 KB and 5,400 allocations per 36 KB layer, all on the default
   allocator (`SourceMap::record`: two tables per array, and a copy of the file text). That
   is 2.6 times the merged tree today. They could be built into the layer's arena, or as
   flat tables indexed by array, since a layer's arrays are all in one arena.
2. **Sessions.**
   - The Config keeps three kinds of `LoadSession`: its base, `read_session()`'s copy, and
     every layer's `after`. Each `State` (defines, packs, origins) is on the default
     allocator; only the `Rc` block is the Config's.
   - `refresh_read` copies a whole `State` on every merge, even when nothing changed.
3. **Script callables (a D61 inconsistency).** Each native's callable, and each `{func}`'s
   `ScriptBody` with its parameter lists, is a `Shared` on the default allocator: about 55
   allocations for the built-ins, plus one per native and four or five per `{func}`.
   `ScriptClass::handlers` also takes no allocator. D61 says function records and classes
   are the owner's; `Shared` was classed as a header type, which is how they slipped.
4. **The first arena block is undersized.** A load's first block is estimated at 4× the
   text; a tree-heavy text needs about 6.1×, so a 102 KB parse took a second and third
   block and used 51% of 1.23 MB. Sizing from a running ratio, or 6×, would save a block
   and most of the slack.
5. **Copies of file text.** Each layer read copies the text twice (into the Config's
   buffer, then the parser's), and a cache read through a custom reader copies the whole
   cache once more.
6. **Results that depend on an owner's allocator.** `Context::eval(text)` builds its tree
   in an arena on the Context's allocator (D64). A value the host keeps from it, and any
   `{func}` that text defined, keeps that arena alive, and with it the allocator's
   `user`. D61's contract (an allocator outlives anything copied out of its owner) covers
   it, but the docs should say so where `eval` is described.
7. **Errors** cost 6–10 allocations each (the record, its message, the stack). That is
   the error path, and acceptable.
8. **Drift in the decisions' text.**
   - D61 lists snapshots under the default allocator; they are the owner's (D62).
   - D64 gives arena blocks 4 KB minimum; the code uses 512 B and the text×4 first block.

## Recommended next steps

In the order of what they save:

1. **Merge (A1 + A2)**, with the Config owning its merge scratch. 9,067 allocations to
   about 19 a merge, 2.4 times faster, a merged tree 14 times smaller. It closes gap 31
   as "keep solo".
2. **Source maps in the layer arena**, or flat tables. About 5,400 allocations and 466 KB
   per layer in edit mode.
3. **Sessions:** state on the owner's allocator, and `refresh_read` copies only on change.
4. **Script callables and handlers** on the Context's allocator, which closes the D61
   inconsistency.
5. **The first arena block** sized for tree-heavy text.
6. **The decisions' text** corrected (D61, D64), and `eval`'s lifetime documented.
