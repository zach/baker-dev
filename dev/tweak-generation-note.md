# Tweaks That Agree Across a Reload: `read_consistent`

A design note, agreed with the owner on Oct 22, 2026, answering a need from hosts that
simulate on worker threads. Results and deviations go into `dev/decisions.md` when it is
built.

## The need

A reload, or a live edit, updates tweaks one at a time on the game's thread. Each tweak's
read is atomic. But a worker reading several at once mid-update can see some old and some
new: the new gravity with the old jump speed, for one physics step.

Today the cookbook's threads section says to read a set of tweaks that must agree on the
game's thread. That does not help an engine whose workers run across the frame's swap
point.

## The design

- **A generation per registry.** `TweakRegistry` holds one `std::atomic<uint64_t>`. Any
  change to its tweaks moves it before the change and again after, so the generation is
  odd while tweaks are changing and even when they are settled.
  - **What counts as a change:**
    - a reload's tweak pass;
    - `Config::set`, `clear_scratch` and `restore`, which retune tweaks;
    - `Tweak<T>::set`, and a layer push, removal or toggle.
  - **One counter per registry.** A reader of physics tweaks may retry because an audio
    tweak changed. Reloads and edits are rare next to reads, so a spurious retry costs
    nothing that matters.
- **`registry.read_consistent(fn)`** runs `fn`, and runs it again if the generation was odd
  when it began or moved before it ended:

  ```cpp
  float g, j;
  baker::TweakRegistry::global().read_consistent([&] {
      g = gravity;      // Tweak<float>
      j = jump_speed;   // Tweak<float>
  });
  ```

  - `fn` must only read: it may run more than once.
  - It returns when one run saw no change, after a short spin, then a yield, while a
    writer holds the generation odd.
- **The ordering, as a seqlock.**
  - **The writer:** `generation.store(s + 1, relaxed)`, then a release fence, then the tweak
    updates (relaxed atomic stores, as today), then `generation.store(s + 2, release)`.
  - **The reader:** `s1 = load(acquire)`, then `fn` (relaxed atomic loads, as a tweak's read
    is), then an acquire fence, then `s2 = load(relaxed)`. The run holds if `s1 == s2` and
    `s1` is even.
- **Which tweaks it covers:** atomic tweaks only, those whose `T` is trivially copyable and
  lock-free (the `kAtomic` ones). A `std::string` or `std::vector` tweak has never been
  safe to read from another thread, and `read_consistent` does not change that. The header
  and the threads section say so.

## Cost

- **Reading a tweak is unchanged:** one load, as D47 measured and the codegen test asserts.
- **`read_consistent` costs two atomic loads and a fence** around the reader's own loads.
- **A change costs two stores and a fence.** On x86 a release fence is a compiler barrier
  alone, and on ARM it is one `dmb`.
  - `Config::set`'s fast path (624 instructions by `tools/count`) gains these.
  - The `set` and `frame` counts are measured before and after, and the result recorded.
- **No allocation and no lock.** The registry's `lock_` still guards only its list.

## Tests to write

- A worker thread reads two tweaks that a reload always sets to equal values, in a loop
  under `read_consistent`, while the game's thread reloads many times. The worker never
  sees them differ. TSan in CI runs it.
- The same through `Config::set` on both.
- `read_consistent` with no change runs `fn` once.
- A non-atomic tweak is left out: a static assertion or a documented rule. Decide when
  building whether to refuse one at compile time.
- Instruction counts: `lookup`, `set` and `frame` before and after.

## Not in this note

- **Consistency of `Config::get` reads from workers.** `cfg.root()` already gives a
  `Ref<Array>` that keeps one whole tree, and a worker that reads through it reads one
  version.
- **Notifying workers that tweaks changed.** The generation could serve as a cheap "has
  anything changed" check (`registry.generation()`), but that is a separate need.
