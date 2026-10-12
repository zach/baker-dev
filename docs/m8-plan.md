# M8 plan: the binary cache

Draft for review. Spec §9 (the `.datb` format), §7 (a 5 MB tree loads in ≤ 10 ms with
≤ 2 allocations), and §10 (M8 row) are the requirements. D57 put three layout fixes in
M8, D64 made trees arena-owned (which the cache builds on), and D69 leaves room in the
budget. The open questions at the end change the design. Once they are answered, the
decisions go into `docs/decisions.md`, and this file is deleted when M8 is done.

## Scope

From the M8 row:

- writing a cache, loading it, the pointer fixups, staleness detection, and the `Cipher`
  hook.

Done when:

1. The cache-load benchmark meets §7: a 5 MB tree in ≤ 10 ms, with ≤ 2 allocations.
2. Cache and text loads produce structurally equal trees, over the whole test corpus and
   the generated benchmark tree.

Carried from D57 (Q1 decides how):

- splitting method and function records into hot and cold parts;
- moving the thunk and callable pointer into the dispatch table;
- a contiguous run of child tags beside an array's children;
- cold-cache benchmark rows: a working set larger than L2.

Not in M8:

- Harmonix's own `.dtb` (spec §9's stretch goal, P3);
- caching script state, tasks, or source maps: a cache holds a tree, and edit mode reads
  text;
- shapes and `#require` (M8b).

## Design

### 1. The format

One file, written little-endian with 64-bit pointers. A loader on another platform
refuses it with an error naming the mismatch.

```
header      magic "MOIRA.DATB", format version, endianness tag, pointer size,
            flags (enciphered), offsets and sizes of the sections below,
            the root's offset, and a hash of the whole file after the header
deps        the files the load read (main and includes): path, size, content hash
defines     the session's defines when the load began, sorted
symbols     count, then each name length-prefixed; an 8-byte slot per symbol that
            the loader overwrites with the interned Symbol
blob        the tree as it lies in memory: arrays first, children adjacent to their
            parents, then strings
```

- **The blob is the arena block (D64).** Each array's and string's 32-bit block offset is
  written as its offset in the blob, so the block header the loader puts in front is all
  the owner bookkeeping a loaded tree needs.
- **Fixups.**
  - A node's payload is written as an offset (arrays, strings) or a symbol index
    (symbols, variables, an array's file).
  - One linear pass over the arrays region turns offsets into pointers and indexes into
    the Symbols the symbol section now holds. Strings need no fixup.
  - Nodes that a parse never makes (`Func`, `Object`) are refused at write time.
- **Macros and `#ifdef` are resolved before writing**, as the spec says: the blob is the
  expanded tree. The header records the defines that were active, so a cache made with
  `DEBUG` defined is stale for a session without it.

### 2. Loading in place

```cpp
auto root = moira::read_cache("level1.datb", session);  // Expected<Ref<Array>>
```

1. Read the file into one allocation from the session's allocator. The Arena's own
   header sits at the front of that allocation, so the arena and its block are a single
   allocation, and the blob begins after the block header.
2. Check the header: magic, version, endianness, pointer size, the file hash, and the
   defines.
3. If the file is enciphered, decipher the blob in place (§4).
4. Intern the symbols into their slots, then run the fixup pass.

That is one allocation for the file. Interning can grow the symbol table, which is
process-wide and amortized (D61). The benchmark runs with a warm table and reports the
cold case beside it.

### 3. Staleness and who writes caches (Q2, Q3)

- **Recommended.** `write_cache(root, path, session)` writes what a load produced, and
  `cache_fresh(path, session)` re-hashes the recorded deps and compares the defines:
  development builds ask, shipping builds trust the cache.
  - `read_cached(text_path, cache_path, session)` is the convenience: it returns the
    cache if it is fresh, else parses the text and rewrites the cache.
  - A command-line tool `moira_bake` writes caches offline, for a build step ("do at
    bake time what you can").
- Content hashes, not modification times: a checkout or a copy changes times without
  changing content, and the reverse happens too. Hashing 5 MB costs about 1 ms, against
  about 35 ms to parse it.

### 4. The Cipher hook

```cpp
struct Cipher {  // the spec's two functions, in place, over the blob
    void (*encrypt)(void* user, std::span<std::byte> bytes);
    void (*decrypt)(void* user, std::span<std::byte> bytes);
    void* user;
};
```

No implementation ships. The original enciphered caches so players would not mail the
studio their own cheat codes; the comment keeps the anecdote. A cache written with a
cipher says so in its header, and loading it without one is an error.

### 5. Config (Q4)

- **Recommended.** `ConfigOptions::cache_dir`: when set, outside edit mode, a layer
  `push_layer(name, path)` reads `cache_dir/<hash of path>.datb` through `read_cached`.
  Reload checks freshness the same way, so a changed file is re-read from text and its
  cache rewritten. Edit mode always reads text, because it needs source maps.

### 6. D57's layout work (Q1)

- **Cold-cache rows first.** `bench_lookup` gains `find_float` and `get` over a working
  set larger than L2, from a text-loaded tree and from a cache-loaded one. A cache-loaded
  tree lays children beside their parent, which may be most of gap 2b's cure by itself.
- **The tag run.** Each array may keep a run of its children's tag indexes beside its
  nodes, so `try_find` scans 4-byte keys instead of loading each child. It costs 4 bytes
  per child. The plan builds it only if the cold rows show the child loads dominating.
- **Dispatch.** Split `Function` and `MethodEntry` into hot (name key, thunk, callable)
  and cold (docs, status, overload list, file and line). Put the single-overload thunk
  and callable pointer in the table entry, removing two dependent loads per call. This
  is its own last step, with `{add 1 2}` and `{bob grow 0}` measured cold and warm.

## Review (acton-review)

| Piece | Path | Cost and evidence |
|---|---|---|
| `read_cache` of a 5 MB tree | load | one read, one allocation, one linear fixup pass: ≤ 10 ms, measured |
| interning the symbol section | load | one hash per distinct symbol; warm and cold rows |
| `cache_fresh` | tool/dev | hashes every dep: about 1 ms per 5 MB; never in shipping |
| `write_cache` / `moira_bake` | offline | not timed against a target |
| tag run (if built) | hot | 4 bytes per child; cold `find_float` must improve to justify it |
| dispatch split | hot | two fewer dependent loads per native call; cold and warm rows |

- **The blob is the arena block,** so a cached tree has the same owner model as a parsed
  one: it is freed as one block, and other trees may depend on it (D64).
- **Deps hashes are the only per-file work at load,** and only when asked.

## Benchmarks

- `bench_cache.cpp`, on the generated 4.2 MB tree that `bench_parse` uses, plus a 5 MB
  variant:
  - text parse (MB/s) beside cache load (ms and allocations, warm and cold symbols);
  - `cache_fresh`;
  - `write_cache`.
- `bench_lookup` cold rows (§6), for text-loaded and cache-loaded trees.
- `bench_call` cold rows for the dispatch split.

## Budget

| Area | Estimated lines |
|---|---:|
| Writer: layout, sections, deps, defines | 160 |
| Loader: checks, in-place read, interning, fixups | 140 |
| `cache_fresh`, `read_cached`, Config's `cache_dir` | 80 |
| `Cipher` | 20 |
| Dispatch split (D57) | 100 |
| Tag run, if the cold rows ask for it | 60 |
| **Total** | **~560** |

`moira_bake` lives in `tools/`, outside the core. That takes the core from 9,126 to about
9,690 of 15,000 (D69).

## Open questions

**Q1. D57's layout work.**
- **Recommended.** Measure first. Add the cold rows, build the cache (whose layout may
  cure gap 2b alone), and build the tag run only if the cold rows still show child loads
  dominating. Do the dispatch split as M8's last step, measured.
- **As D57 says.** All three, regardless of measurement.
- **Separate milestone.** Move D57 after M8b, and keep M8 to the cache.

**Q2. Staleness.**
- **Recommended.** Content hashes of every file the load read, plus the define set,
  checked only by `cache_fresh` and `read_cached`. Shipping trusts the cache.
- **Size and modification time.** Cheaper, but checkouts and copies make it wrong both
  ways.
- **None.** The build step owns freshness, and the library only reads and writes.

**Q3. Who writes caches.**
- **Recommended.** Both: a `moira_bake` tool for build steps, and `read_cached` for
  development, which rewrites a stale cache on the fly.
- **Bake only.** Runtime never writes files. Simplest, but development pays the text
  parse every launch until someone bakes.

**Q4. Config.**
- **Recommended.** `ConfigOptions::cache_dir`. Outside edit mode, layers load through
  `read_cached`, and reload rewrites the caches of changed files.
- **Not in Config.** The host calls `read_cache` and pushes the tree as an overlay layer.
  Less code, but reload then cannot tell a layer came from a file.
