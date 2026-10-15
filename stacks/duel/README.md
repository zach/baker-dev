# Duel

Player versus computer fighting on frame data (`dev/background/game-stacks.md` §4.6). The
question it exists to answer: does the fight feel good, with risk and reward on every move,
and a CPU that is beatable, fair, and teaches?

```sh
build-stacks/bin/stacks_duel                              # play: you on the left, the CPU on the right
build-stacks/bin/stacks_duel sim --seeds 1-10             # the CPU against the bots, and the frame data
build-stacks/bin/stacks_duel eval "{duel.frame_table}" "{duel.combo_route}"
```

**Keys.** A and D (or the arrows) walk, S crouches, holding back blocks (crouching blocks a
low). J light, K heavy, L kick, I special; crouch for a crouching move. A gamepad works too:
the stick or pad, and X, Y, A, B. P pauses and period steps one frame; F1 shows the panels,
with the Duel window: who plays each side, the frame table, and the combo search.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `fight.hpp`, `fight.cpp` | L2: the fight, in integers; the pilots; the frame table and combo search | Engineers |
| `main.cpp` | L2: input, script words (`duel.`), drawing, the Duel panel | Engineers |
| `packs/game/duel_core.dta` | L2: the shapes of a fighter, a move, a CPU level, a bot | By pull request |
| `data/default.dta` | L4: rules, the fighter, the CPU's difficulties, the bots | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/` | Scenarios: each difficulty against each bot; the frame data | The agent |
| `metrics/duel.dta` | Objectives: no infinites, a combo ceiling, block advantage, the reaction floor, the win-rate curve | By pull request |

## Design

- **Integers all the way** (the owner's decision): time in frames, distance in millimetres,
  chances in per mille. The data's metres and fractions are rounded once, when the data
  loads, so a fight is the same on any compiler and CPU, and a replay is its inputs.
- **One step is one frame.** `Game::step` runs one frame of the fight per fixed step of
  the base (60 Hz); the fight never sees the clock.
- **Frame rules.** A move is active from frame `startup + 1`; on contact both fighters
  freeze for `duel.hit_pause` frames; the defender is stunned for `hitstun` frames after
  the contact frame, less `duel.stun_decay` for each hit already in the combo. So advantage
  on hit is `hitstun + 1 - active - recovery`, the frame table's formula, and a test checks
  the fight agrees with it. A press waits `duel.buffer` frames for its fighter to act; a
  move that connected may be cancelled into any of its `cancels`. Pushback slides the
  defender; at a wall the attacker slides back instead.
- **Pilots.** The CPU and the bots are one mechanism with different numbers (`(cpu ...)`,
  `(bots ...)`): it sees the opponent `reaction` frames late; at each decision near the
  opponent it may guard (standing, crouching only for a low it has seen); it raises its
  guard for an attack it sees, if the attack is slow enough; out of blockstun it punishes
  what it knows is unsafe; it attacks in range, and cancels what connected. The fastest it
  ever changed its guard for something it saw is `{duel.fastest_reaction 2}`, so the
  reaction floor is an objective.
- **The combo search** (`{duel.max_combo_damage}`, `{duel.infinites}`, `{duel.combo_route}`)
  runs the fight itself: from touching range, mid-stage and in the corner, each opener, then
  every cancel and every link (walking in 0 or more frames first) that hits before the
  opponent recovers, memoized by the positions and the hit count. A combo that reaches 40
  hits is an infinite.

## Deviations from §4.6

- **CPU difficulties are keyed data, not layers.** §4.6 proposed `cpu/easy.dta` and
  `cpu/hard.dta` as layers toggled at run time. Every layer above `default` is shared by
  every difficulty, so the agent's layer could tune only all of them at once; keyed data
  under `(cpu ...)` lets `agent.dta` tune `cpu.hard.block` alone, and the Duel panel
  switches difficulty in one session, which was the point of the layers.
- **Not yet:** jumping, throws, projectiles, armour, and invincibility (push-up candidates:
  a field the fight does not know is a shape warning in `check`); training mode's record
  and playback of the dummy; damage scaling.

## Measured

See `dev/decisions.md` D109.
