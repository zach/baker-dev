# Working on Baker

Documentation is in two places (D152): `docs/` is the user documentation, which every
release ships whole, and `dev/` is the development documentation (decisions, spec,
background research, assessments, the release process), which never ships. A new guide for
users of the library goes in `docs/`; a plan, assessment, review or status note goes in
`dev/`.

Read `dev/spec.md` for what Baker is (§10 lists the milestones), `dev/decisions.md` for
every deviation from it and the results of each step, and the current milestone plan
(`dev/m<N>-plan.md`) if one exists. M10, the harness instrumentation, is done (D96):
every milestone of spec §10 is. The core's soft line budget is 15,500 (D69, D220);
`tools/count_lines.py` reports it, and `baker::shape` apart. CI also checks
`clang-format-18 --dry-run --Werror` over every source file, and runs clang-tidy over `src/`
with `.clang-tidy`, where every finding is an error (D103).

`stacks/` is a separate CMake project over this Baker: the game stacks of
`dev/background/game-stacks.md`, for Windows PC (`stacks/README.md`, D106). The rules
below are for Baker; the stacks keep Baker's no-exceptions rule and formatting, build with
warnings as errors, and have their own CI job.

## Releases

The core library is published to github.com/zach/baker, one commit per release, tagged
with its semantic version (`dev/releasing.md`, D150).

- **Never start, prepare, or publish a release on your own initiative.** Run the Release
  workflow, `publish.py bump`, or `publish.py commit` only when the owner asks for that
  release in so many words, and run the workflow with *publish* checked only when they ask
  to publish.
- What ships is public: write `README.md`, the user docs and `CHANGELOG.md` for users of
  the library. Citations in parentheses, such as `(D77)` or `(spec §5)`, are stripped on the
  way out; one in running text is not, and `publish.py check` (CI, the local matrix) fails
  until it is reworded.

## Integrations

Baker stays MIT and free of other parties' terms; `INTEGRATIONS.md` is the policy for trying
it with copyleft, source-available, commercial or NDA platforms. In this repository:

- **Add no dependency on a restrictively licensed target,** not even optional: no includes,
  vendored code, CMake options, `FetchContent`, CI jobs or tests that reach one. An
  integration is its own repository (`tools/integration-template/` starts one), and it
  depends on Baker, never the reverse.
- **Build a feature an integration motivates from its requirement note alone.** The owner
  brings the note's text; design the feature as Baker's own, named and shaped for Baker's
  users, and test it against a mock host in Baker's tests. Do not look up, quote or imitate
  the target's code, headers or documentation to write it, and do not write Baker code from
  memory of an integration's.
- **Keep integration work out of Baker sessions.** If a conversation turns to working inside
  an integration repository, or brings in a target's code or confidential material, stop and
  ask the owner to move that work to a session in the integration's repository.
- **Write Baker's record in Baker's terms.** A D-entry, changelog entry, comment, commit
  message or PR gives the general motivation (for example, hosts that stream content). It
  may name a public product as an example user. It never describes a target's internals or
  anything an agreement keeps confidential, and never calls a Baker feature equivalent to,
  or derived from, another product's.
- **When in doubt whether something may cross into Baker, it does not.** Ask the owner with
  AskUserQuestion.

## Rules

- **Breaking changes are allowed and encouraged.** Do not keep backward-compatibility
  layers, deprecated aliases, or unused legacy code paths. When an API changes, change
  every caller in the repository and delete the old form. There are no downstream users
  to protect. (Script-level `Status::Deprecated` is a feature for script authors, not a
  compatibility layer.)
- Record each deviation from `dev/spec.md` in `dev/decisions.md` (spec §11). Results of
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
  benchmark files (`tools/bench_compare.py` does both builds and interleaves the runs).
  Record numbers, regressions and noise included. When a row moves, count instructions
  before calling it code layout: `tools/count_instructions.py` runs fixed workloads under
  cachegrind on both commits.
- **CI** also runs MSVC, libc++, and TSan, which the local matrix cannot. Check it after
  each push.
