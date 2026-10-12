# Working on Moira

Read `docs/spec.md` for what Moira is (§10 lists the milestones), `docs/decisions.md` for
every deviation from it and the results of each step, and the current milestone plan
(`docs/m<N>-plan.md`) if one exists. M9 is done (D89); M10, the harness instrumentation,
follows `docs/m10-plan.md` (answers in D90). The core's soft line budget is 15,000 (D69);
`tools/count_lines.py` reports it, and `moira::shape` apart. CI also checks
`clang-format-18 --dry-run --Werror` over every source file.

## Rules

- **Breaking changes are allowed and encouraged.** Do not keep backward-compatibility
  layers, deprecated aliases, or unused legacy code paths. When an API changes, change
  every caller in the repository and delete the old form. There are no downstream users
  to protect. (Script-level `Status::Deprecated` is a feature for script authors, not a
  compatibility layer.)
- Record each deviation from `docs/spec.md` in `docs/decisions.md` (spec §11). Results of
  a step go in a D-entry too, with benchmark numbers.
- Hold designs and hot-path changes to `.claude/skills/acton-review/` (the skill and its
  `references/moira-gaps.md` status table). Write a short design note before a step that
  changes ownership or the API.
- **Ask the owner each open question with the AskUserQuestion tool,** one question per
  decision with the recommended option first, rather than listing questions in prose.

## How the work is done

- **Commit each sub-step green.** Before every commit, run `tools/local_matrix.sh`:
  debug, release, C++23, shipping, ASan+UBSan, Clang, data-only, plain refcounts, EASTL,
  and the ImGui harness (the last two need the network the first time). Shipping refuses live edits and
  reloads, so test those under `if constexpr (!kShipping)`.
- **Containers in `src/`** are `Vec`, `Str`, `HashMap`, `Own`, and `Rc` from
  `src/common/containers.hpp`, never the STL's directly (D55, D61). An owner passes its
  allocator to the tables it keeps. Do not construct a `Vec` on a hot path, even an empty
  one: MSVC's debug library allocates for it (D62). Pass pointer ranges, not `std::span`
  iterators, to EASTL.
- **Trees** (D64): a load builds in one arena; anything else is a solo block. Code that
  builds arrays while a load is being read must build them in that load's arena, or the
  load counts itself and leaks.
- **Gates.** `ctest` runs `tools/check_headers.py` (public headers reach only D55's
  standard headers), compiles every public header alone, and runs `moira_alloc_tests`,
  which fails if a Context or Config reaches `operator new`.
- **Benchmarks.** Measure before and after with `bench/` (`-DMOIRA_BUILD_BENCH=ON`),
  against a git worktree of the previous commit on the same machine, with the same
  benchmark files. Record numbers, regressions and noise included. When a row moves,
  count instructions with cachegrind in a small driver before calling it code layout.
- **CI** also runs MSVC, libc++, and TSan, which the local matrix cannot. Check it after
  each push.
