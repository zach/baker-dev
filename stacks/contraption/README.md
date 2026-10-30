# Contraption

Physics puzzles, side on (`dev/background/game-stacks.md` §4.1). The question it exists to
answer: is this level solvable, solvable only the intended way, and does solving it feel
clever rather than fiddly?

```sh
build-stacks/bin/stacks_contraption                          # play
build-stacks/bin/stacks_contraption sim --baker-seeds 1-5    # every level: intended, robust, uncheesed
build-stacks/bin/stacks_contraption eval "{puzzle.load high_shelf}" "{puzzle.fewest_pieces 300}" "{puzzle.cheese_route}"
```

**Playing.** Pick a piece from the tray in the Contraption window. Left click places it
where the mouse is (snapped to `contraption.grid` and `contraption.angle_step`); right click
takes back the nearest; the wheel, or Q and E, turns it. Space runs the machine and stops it,
R resets, N and P change level. "intended" shows the designer's solution as ghosts; the
window also runs the robustness check and the cheese search for the open level.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `puzzle.hpp`, `puzzle.cpp` | L2: kinds and levels from data, a run as a Jolt world, attempts, robustness, the cheese search | Engineers |
| `main.cpp` | L2: placement with the mouse, the play loop, script words (`puzzle.`), the window, drawing | Engineers |
| `packs/game/contraption_core.dta` | L2: the shapes of a kind, a level, a piece, a placement (all `(@keyed)`) | By pull request |
| `data/default.dta` | L4: the kinds and the levels | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/levels.dta` | Scenarios: each level judged; the intended solution played on the game's own steps | The agent |
| `metrics/puzzle.dta` | Objectives: intended solves, not cheesed, robust, brisk, played solves | By pull request |

## Design

- **Side on**, the owner's choice: every moving body is locked to the x-y plane and turns
  about z (Jolt's `Plane2D`), so placement with a mouse is two numbers and an angle, and
  the searches stay small. Rendered in 3D.
- **A run is a fresh Jolt world** (`base::Physics`): the floor, the level's pieces, then the
  placements, in that order, stepped at 60 Hz; the same placements replay bit for bit, on
  any build (a test pins the hash). An attempt ends when the goal has held for
  `contraption.hold_seconds`, at `contraption.run_seconds`, or as soon as everything is at
  rest and the goal does not hold.
- **The goal is a command** in the level, `{puzzle.in ball cup}`, evaluated each step about
  the run being judged, so a designer can write any goal the words allow.
- **Kinds** come from `(kinds ...)`: a box, a sphere, or a bucket (open at the top), with
  mass (none: it never moves), friction, bounce, and `hinge` for a seesaw pinned at its
  center. Tray pieces of a massless kind (planks) stay where they are put.
- **The analyses** (§4.1, "Agent loop"): `{puzzle.solves_intended}`;
  `{puzzle.robustness samples metres degrees}`, the share of perturbed intended solutions
  that still solve; `{puzzle.fewest_pieces tries}`, a random search over the tray for
  solutions with fewer pieces than intended, whose find `{puzzle.cheese_route}` prints as
  data a scenario can place.

## The levels, and how they were designed

Each level was laid out by searching placements headlessly (the same `attempt`), then given
an intended solution from the region that solved most robustly: `first_drop` (one plank: 4
of about 800 coarse placements solve), `high_shelf` (two planks; neither alone solves, and
3,000 one-plank tries over seeds 1–30 found no cheese), and `catapult` (a crate dropped on a
seesaw throws the ball).

## Not yet

Fans, springs, magnets, conveyors (push-up candidates: a kind field the puzzle does not know
is a shape warning); the ghost of the intended solution while it runs; a mouse cursor with a
gamepad (§4.1's "gamepad cursor" robustness); generation of level variants.

## Measured

See `dev/decisions.md` D111.
