# Working on Baker

Read `docs/spec.md` for what Baker is (§10 lists the milestones), `docs/decisions.md` for
every deviation from it and the results of each step, and the current milestone plan
(`docs/m<N>-plan.md`) if one exists. M10, the harness instrumentation, is done (D96):
every milestone of spec §10 is. The core's soft line budget is 15,000 (D69);
`tools/count_lines.py` reports it, and `baker::shape` apart. CI also checks
`clang-format-18 --dry-run --Werror` over every source file, and runs clang-tidy over `src/`
with `.clang-tidy`, where every finding is an error (D103).

`stacks/` is a separate CMake project over this Baker: the game stacks of
`docs/background/game-stacks.md`, for Windows PC (`stacks/README.md`, D106). The rules
below are for Baker; the stacks keep Baker's no-exceptions rule and formatting, build with
warnings as errors, and have their own CI job.

## Releases

The core library is published to github.com/zach/baker, one commit per release, tagged
with its semantic version (`docs/releasing.md`, D150).

- **Never start, prepare, or publish a release on your own initiative.** Run the Release
  workflow, `publish.py bump`, or `publish.py commit` only when the owner asks for that
  release in so many words, and run the workflow with *publish* checked only when they ask
  to publish.
- What ships is public: write `README.md`, the user docs and `CHANGELOG.md` for users of
  the library. Citations in parentheses, such as `(D77)` or `(spec §5)`, are stripped on the
  way out; one in running text is not, and `publish.py check` (CI, the local matrix) fails
  until it is reworded.

## Rules

- **Breaking changes are allowed and encouraged.** Do not keep backward-compatibility
  layers, deprecated aliases, or unused legacy code paths. When an API changes, change
  every caller in the repository and delete the old form. There are no downstream users
  to protect. (Script-level `Status::Deprecated` is a feature for script authors, not a
  compatibility layer.)
- Record each deviation from `docs/spec.md` in `docs/decisions.md` (spec §11). Results of
  a step go in a D-entry too, with benchmark numbers.
- Hold designs and hot-path changes to `.claude/skills/acton-review/` (the skill and its
  `references/baker-gaps.md` status table). Write a short design note before a step that
  changes ownership or the API.
- **Ask the owner each open question with the AskUserQuestion tool,** one question per
  decision with the recommended option first, rather than listing questions in prose.

## How the work is done

- **Commit each sub-step green.** Before every commit, run `tools/local_matrix.sh`:
  debug, release, C++23, shipping, ASan+UBSan, Clang, data-only, plain refcounts, EASTL,
  and the ImGui harness (the last two need the network the first time), then clang-tidy.
  Shipping refuses live edits and reloads, so test those under `if constexpr (!kShipping)`.
- **Containers in `src/`** are `Vec`, `Str`, `HashMap`, `Own`, and `Rc` from
  `src/common/containers.hpp`, never the STL's directly (D55, D61). An owner passes its
  allocator to the tables it keeps. Do not construct a `Vec` on a hot path, even an empty
  one: MSVC's debug library allocates for it (D62). Pass pointer ranges, not `std::span`
  iterators, to EASTL.
- **Trees** (D64): a load builds in one arena; anything else is a solo block. Code that
  builds arrays while a load is being read must build them in that load's arena, or the
  load counts itself and leaks.
- **Gates.** `ctest` runs `tools/check_headers.py` (public headers reach only D55's
  standard headers), compiles every public header alone, and runs `baker_alloc_tests`,
  which fails if a Context or Config reaches `operator new`.
- **Benchmarks.** Measure before and after with `bench/` (`-DBAKER_BUILD_BENCH=ON`),
  against a git worktree of the previous commit on the same machine, with the same
  benchmark files. Record numbers, regressions and noise included. When a row moves,
  count instructions with cachegrind in a small driver before calling it code layout.
- **CI** also runs MSVC, libc++, and TSan, which the local matrix cannot. Check it after
  each push.
