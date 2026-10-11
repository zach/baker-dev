# M5b plan: engine hygiene

Work the project owner asked for now (2026-10-11), ahead of the rest of M6, so that M6's
messages, statistics, and tracing are built on the new API from the start. The decisions
are D54–D56; this file orders them into steps. It is deleted when the work is done, as the
milestone plans are.

Each step ends green on the whole CI matrix, with benchmarks re-recorded against M6
step 1 (`{add 1 2}` at 34 ns, D52).

## Steps

1. **Remove the throwing forms, and drop `try_` (D54).** *Done: D59.*
   - Delete `Exception`, `raise`, the `MOIRA_EXCEPTIONS` switch, and the D52 recorder hook.
   - Rename every `try_x` to `x`.
   - Convert tests, examples, and benchmarks.
   - CI: replace the "no exceptions" jobs with `-fno-exceptions` on every job.

   This step is mechanical and shrinks the code. It goes first so later steps touch half
   as much API.
2. **Standard-library-free public headers, and `Error` as a handle (D55).** *Done: D60.*
   - Turn `Error` into a handle to an error record.
   - Turn callbacks into a function pointer plus `void*`.
   - Move containers, `std::string`, `shared_ptr`, and `std::function` out of public
     declarations, into `src/` or behind opaque types. `string_view`, `span`, and
     `optional` stay.
   - Add a CI test that compiles each public header alone and fails on any standard
     header outside D55's list.
   - Measure: preprocessed lines and compile time of a translation unit that only reads a
     `Tweak<float>`, before and after; `sizeof(Expected<Node>)`; `{add 1 2}`.
3. **Allocators and the container alias layer (D55, D56).**
   - Add `Allocator`.
   - Make `Context` and `Config` take an allocator.
   - Add a default allocator for process-wide state.
   - Move `src/` containers onto the alias layer with Moira's allocator.
   - Add a `MOIRA_USE_EASTL` CI job.
   - Test: a counting allocator sees every allocation a Context and a Config make, and
     the global `operator new` counter sees none.
4. **An arena per load (D56).** Parsed trees come from per-load arenas. Arrays lose their
   per-array refcount. Arrays built after load go to a scratch arena. This is the largest
   step and touches:
   - `Ref<Array>` (D20);
   - macro sharing;
   - `set` and the scratch layer (D42);
   - snapshots;
   - reload.

   It gets its own short design note before code, answering D56's open point: how a value
   that outlives its load keeps its tree alive.

Then M6 resumes: typed messages with `handles<M>` and `handle_all`, call statistics, and
the trace hook (D53).

## Budget

| Step | Estimated lines |
|---|---:|
| 1. Throwing forms removed | −150 |
| 2. `Error` handle, callbacks, headers | +150 |
| 3. Allocators, alias layer | +200 |
| 4. Arenas | +250 |
| **Net** | **~+450** |

That takes the core from 7,262 to about 7,710 of 10,000, before the rest of M6 (~400).

## Risks

- **Step 4** changes ownership everywhere. If the outliving-values question has no clean
  answer, the fallback is a refcount per load, not per array. It still closes gap 5's
  cost.
- **EASTL** has its own build requirements. If fetching it in CI is unreliable, the job
  pins a commit and caches the download.
- **The renames in step 1** break any downstream code. There is none outside this
  repository yet, which is why now is the time.
