# Harness audit of the stacks, 2026-10-14

The method of `dev/background/reading-a-studio.md` §7, run on the seven games in `stacks/`
after every stack of `dev/background/game-stacks.md` §4 was built (D112, D113), as §6 of
that document asks at each step. The results are recorded in D114.

**What this audit is not.** The method is for a studio: stopwatches on real work, and
interviews with two or three users per harness. The stacks have one person (the owner) and
an agent, and no users yet. So the loop times are measured by a script, not watched; the
dimensions profile is a reading of each harness by its builder, not interview answers; and
voluntary use is unknown everywhere. Those columns are marked as such. Re-run the audit
with people once anyone but the owner plays a stack.

**Where measured.** Release builds on the development container (Linux, a 4-core Xeon at
2.8 GHz), the games rendering with OpenGL under Xvfb. Not the minimum-spec PC of
game-stacks.md §9; D106 measured the cube there (edit-to-see median about 25 ms).

## 1. The register

One row per harness (step 1). "Built by" and "Used by" name roles of game-stacks.md §9; in
fact each is the owner and the agent today.

| Harness | Layer | Built by | Used by | Notation | Edit-to-feel (median / worst) | Extensible without rebuild by | Bus factor | Voluntary use? | Stage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Data files (`data/*.dta`) in an editor, reloaded live | L4 | Engineers (shapes), designers (values) | Designers, the agent | Baker data | 5–17 ms / 24 ms; Wayfarer 174 ms / 215 ms (§2) | Anyone: it is data | 1 (the owner) | Not asked | Product (Baker) |
| Baker's panels (F1: tweaks, console, loop, layers, journal, shapes, snapshots) | L1 | Baker (`examples/harness-imgui`) | Everyone at the game | Panels over tweaks and layers | One frame (a tweak is in memory) | Nobody: C++ | 1 | Not asked | Product |
| Each game's own window (tray, timeline and "why", map and gating, ...) | L3 | Gameplay engineers | Designers | ImGui | One frame | Nobody: C++ | 1 | Not asked | Custom-built |
| Script words in data (`#autorun {func}`, `{class}`, `{extend}`) | L2–L3 | Technical designers | Designers, the agent | Baker script | As the data files | Anyone who writes script | 1 | Not asked | Custom-built |
| Shapes and packs (`packs/game/*_core.dta`) | L2 | Engineers | Everyone, through `check` | Baker shapes | As the data files | Anyone, by pull request | 2 roles named per pack; 1 person | Mandated | Product |
| The agent's commands (`sim`, `check`, `reference`, `diff`, `promote`, `eval`) | L1 | The base | The agent, engineers | Command line, Baker data out | `check` 0.01–0.09 s; `sim` one seed 0.01–1.05 s | Nobody: C++ (a new measure is a script word) | 1 | The agent's only way in | Custom-built |
| Scenarios and objectives (`sim/*.dta`, `metrics/*.dta`) | L3 | The agent (scenarios), people (objectives) | The agent | Baker data | One `sim` run | The agent (scenarios); people, by pull request (objectives) | 1 | Not asked | Custom-built |
| The remote console (`tools/console.py`, the console panel) | L1 | Baker (`baker::host::Console`) | The agent, engineers | One command per line | One frame | Anyone who writes script | 1 | Not asked | Product |
| Ledger's economy sheet (`economy.xlsx` → `export_economy.py` → `economy.dta`) | L3 (adopted fled harness) | A systems designer | Systems designers | Spreadsheet → export | Export 0.07 s, then the data file's reload | Its owner | 1 | Yes, by design (D109) | Custom-built |
| Game C++ (`stacks/*/`, the natives) | L2 | Gameplay engineers | Engineers | C++ | Rebuild 2–6 s, then a restart that loses the session's state | Engineers only | 1 | Mandated | Custom-built |

**Read at a glance.** The bus factor is 1 everywhere: every pack names two owner roles, as
`check` asks, but one person fills all of them. `check` cannot see that; only this register
can. No harness is mandated where an alternative exists, and the one fled harness (Ledger's
sheet) is adopted, with `origin` carrying it into the reference.

## 2. Loop times

Step 2, measured by `{live.loop}` (the new console word over `Config::loop_stats()`):
ten saves of a tweak in each game's `local.dta`, 0.7 s apart, while the game ran. Edit-to-see
runs from the file's save time to the frame that presented the change; the reload is
Baker's part of it.

| Game | Edit-to-see median / worst | Reload median / worst | What the rest is |
| --- | --- | --- | --- |
| Cube | 5.4 / 10.0 ms | 0.12 / 0.40 ms | Waiting for the next frame |
| Duel | 6.3 / 11.6 ms | 0.13 / 0.20 ms | The same |
| Ledger | 12.3 / 15.1 ms | 0.16 / 1.15 ms | `Game::load` rebuilds the economy |
| Contraption | 10.0 / 18.3 ms | 0.19 / 0.24 ms | `Game::load` rebuilds the catalog |
| Stuntline | 12.4 / 23.9 ms | 0.20 / 4.17 ms | `Game::load` re-lays the track |
| Hearth | 17.0 / 24.4 ms | 0.21 / 0.23 ms | `Game::load` respawns the household and its navigation mesh |
| **Wayfarer** | **174.1 / 215.1 ms** | 0.23 / 0.47 ms | The gating view rebuilds all nine navigation tiles of the analyses (about 16 ms each) after every reload, even for a run speed |

Every stack is inside the one-second target of game-stacks.md §2.2 by a wide margin, and
Baker's share is under a quarter of a millisecond. Wayfarer is ten times the others, and
all of it is the game's own: the finding of the audit (§6).

The other loops: a C++ change is a 2–6 s rebuild and a restart that loses the running
state (the tuned values survive only through write-back); the agent's loop is `check` in
0.01–0.09 s and one seed of `sim` in 0.01–1.05 s (Hearth's 50-household soak is the
slowest).

## 3. Dimensions profile

Step 3, as the builder reads each harness, on the dimensions of reading-a-studio.md §3.
Not interviews.

| Harness | Closeness of mapping | Viscosity | Hidden dependencies | Premature commitment | Progressive evaluation | Error-proneness |
| --- | --- | --- | --- | --- | --- | --- |
| Data files | Close: a level is its pieces, a mission its objectives | Low: one line per value; keyed shapes let a layer change one entry | Visible: `diff` and the layers panel show what each layer sets | Low | Immediate: the next frame | Typos caught by `check` and shapes, at load |
| Script words in data | Close for actions ("walk, wait, use"), far for goals the proof cannot read | Low | A `{class}` after an `{extend}` drops it; the order is the layers' | Low | Immediate | `check` catches wrong arity and unknown words; `{wait}` outside a task is an error |
| Game windows | Close: each shows its stack's question (why, gating, cheese) | None: read-only views and buttons | The analyses' state is shown only after a button | n/a | A button runs the analysis | Low |
| Agent commands | Medium: results are data, one line per run | Low | Objectives live apart from the data they judge, by design | The agent must write the scenario first | Seconds | A failed command exits non-zero with the file and line |
| Economy sheet | Closest for its users | Low in the sheet; the export is one command | The generated file must not be edited (it says so on every line) | Low | After the export | `--check` fails if the export is stale |
| Game C++ | Far for designers | High: a rebuild and a restart | Natives the data calls are visible only in the reference | High: a word is fixed until the next build | Seconds plus a restart | Compiler and tests |

## 4. Who can extend what

Step 4. The words a designer or the agent can add without a rebuild are script: `{func}`
for functions, `{class}` and (since D113) `{extend}` for handlers, scenarios, and keyed
data. Natives, panels, the game windows, and the analyses (the proof, the bots, the soak)
need C++. Every layer has one person able to extend it. That is the finding: not a missing
gardener at one boundary, but one gardener for all of them, which a single-person project
cannot fix by tooling. When a second person joins, give them a stack's L2 first.

## 5. Demand and flight signals

**Demand in the data (step 5): none.** `check` over each stack's data, scenarios, and
objectives reports no errors, warnings, unresolved words, stubs, repetitions, or findings,
in all seven. The stacks were each built with their vocabulary in hand, so the data has had
no time to ask for more.

**Demand in the code: the base and Baker are missing words every stack wrote again.**
`check` reads data, not C++; a grep of the stacks does:

| Helper, written again | In | What it says |
| --- | --- | --- |
| `tag_of(array)`: an array's first symbol | 6 of 6 games with structured data | Baker has `find(tag)` but no accessor for an array's own tag: demand at L0 |
| `get(array, tag, fallback)` and `nth(array, tag, i)`: a float, or the i-th value of `(tag a b c)` | 4 and 2 | `find_float(tag, fallback)` returns `Expected`, so every caller repeats the fallback in `value_or`. A value by position has a reader already, a path (`get<double>("pos.1", 0)`), which no stack found |
| A seeded xorshift (`next`, `uniform`) | 6 (Duel's inline) | The base has no RNG for simulations |
| `not_found(what)`: a NotFound error for a script word | 5 | A one-line convenience the base or Baker could own |
| `box(renderer, ...)`: a box from loose floats | 5 | `base::Box` has no constructor for the common case |
| `truthy(node)`: a command's result as a goal | 3 | Every stack with goals as commands (puzzle, milestones, missions) decides truth itself |

These are the "third time" rule of the vision doc, met in C++ rather than data.

**Recurring console sequences.** Every analysis is two words, run then report:
`{hearth.soak 50 30} {hearth.soak_report}`, `{world.prove} {world.proof_report}`,
`{world.pacing STYLE 1800} {world.pacing_report}`, `{track.load T} {track.report}`,
`{econ.compete} {econ.advance_days 720} {econ.report}`. The agent's skill documents each
pair. A stack convention (a `report` that runs what it reports) would halve them; not worth
a new mechanism yet.

**Flight (step 6).** One external harness, Ledger's spreadsheet, adopted by design: its
export is `origin`-tagged and listed in Ledger's reference. No private scripts bypass a
tool. The voluntary-use question needs users.

## 6. Evolution map

Step 7. The most-used words, by layer and stage (genesis → custom-built → product →
commodity).

| Words | Layer | Stage | Note |
| --- | --- | --- | --- |
| SDL3, bgfx, Dear ImGui, Jolt, Recast/Detour | L0–L1 | Commodity, adopted | Nothing in-house where the industry has converged |
| Baker (data, layers, shapes, script, `check`, `loop_stats`) | L0 | Product | The library under every harness |
| `Live`, `FixedStep`, `Physics`, `NavMesh`, `sim` | L1 | Custom-built → product | Thin wrappers; the loop, watcher, console and commands moved into Baker's host kit (`baker::host`), and `Live` is the stacks' conventions over it |
| `tag_of`, RNG, `box`, `not_found`, `truthy` | L2, five or six copies | Custom-built, ready for product | §5: push up to the base and Baker |
| `puzzle.*`, `track.*`, `duel.*`, `econ.*` | L2 | Custom-built | Each stack's vocabulary |
| `hearth.*`, `world.*`, `mission.*`, `at_poi`, `flag`, `has_item` | L2 | Custom-built | Newest; the objective natives match §4.4 exactly |
| The analyses: the cheese search, the proof, pacing bots, the soak | L2 (C++) | Genesis, hardened into code | Worth watching: each is one algorithm fixed in C++; a second stack wanting one is the signal to make it a base module (game-stacks.md §5 already names "bot drivers and the runner's statistics" as shared) |
| Villager actions (`use_object`, `eat_snack`, ...) | L3 | Genesis | Script, as they should be |

No commodity capability is built in-house. The genesis-stage ideas hardened into C++ are
the analyses, by design: they are the stacks' questions, and each exists once.

## 7. Interventions

Step 8: three, by the leverage of reading-a-studio.md §8, to re-measure at the next audit.

1. **Fix Wayfarer's loop** (rank 1, the loop). Keep the analyses' navigation mesh until the
   geometry it depends on changes (the cells, `climb_max`, `step`, the glide ratio), so a
   tweak reloads in a frame like the others. Re-measure: Wayfarer's edit-to-see.
2. **Push the repeated helpers up** (rank 9, prune and commoditize; and pushing-up.md).
   `Array::tag()` and a positional reader in Baker; a seeded RNG, `Box` constructors,
   `not_found`, and command truth in the base; delete the copies. Re-measure: the copies
   (31 across the stacks) go to zero.
3. **A second gardener** (rank 3), when there is a second person: hand one stack's L2 to
   them and re-run the register's bus-factor column. Not a code change.

And the audit itself gained an instrument: `{live.loop}`, so step 2 is a script, not a
stopwatch (rank 2, instrument the tree).

## 8. Re-measured, after the first two

Applied the same day (D115), with the owner's go-ahead, and measured again.

- **Wayfarer's loop.** The analyses' mesh is kept until `wayfarer::nav_key` changes (every
  cell's source, the cell size, `climb_max`, `step`, the glide ratio). Ten saves of a tweak:
  edit-to-see **15.9 ms median, 19.9 ms worst** (174.1 / 215.1 before), reload 0.16 ms.
  Wayfarer is now with the others. The key also fixes what the old flag missed: a
  `climb_max` tuned in a panel used to leave the analyses on the old mesh until a reload.
- **The copies.** Baker gained `Array::tag()`; the base gained `base::Rng` (`rng.hpp`),
  `base::name`, `base::number`, `base::not_found`, `base::truthy` (`words.hpp`), and
  `Renderer::box(x, y, z, w, h, d, r, g, b, yaw)`. Copies left: **1 of 31**, Contraption's box
  helper, which turns about z, takes a quaternion, and draws spheres and buckets: a
  specialisation, not a copy. The stacks are 112 lines shorter (408 removed, 296 added,
  the new headers included). Every stack's `sim` over seeds 1–3 gives byte-identical output
  before and after, and the pinned replay hashes hold: each stack keeps its own seeding,
  as a stream of `base::Rng`.
- **`check`** still reports nothing in any stack: there was no demand in the data to clear.

